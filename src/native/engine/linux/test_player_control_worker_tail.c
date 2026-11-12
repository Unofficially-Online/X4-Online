#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PLAYER_TAIL_TEST
#define GATE_PAGE 4096u
#define TEST_BASE UINT64_C(0x1212000)
static unsigned char *arena;
#define LX(address) (arena + ((uintptr_t)(address) - TEST_BASE))
static unsigned open_calls, close_calls, cas_calls, page_rx_calls;
static unsigned fail_open, fail_close, fail_cas, fail_page_rx, reject_pins;
static int is_code_rx = 1, is_stub_rx;
static unsigned callback_calls, release_calls, expected_order;
static void *seen_semaphore;
static unsigned seen_count;
static int seen_errno;
static void synthetic_release(void *semaphore, unsigned count) {
    assert(expected_order == 1);
    expected_order = 2;
    ++release_calls;
    seen_semaphore = semaphore;
    seen_count = count;
    seen_errno = errno;
}
#define PLAYER_TAIL_READ(address, output, size) (memcpy((output), (address), (size)) != NULL)
#define PLAYER_TAIL_ORIGINAL(semaphore, count) synthetic_release((semaphore), (count))
static int player_tail_pins(void) {
    return !reject_pins;
}
static unsigned char *player_tail_allocate(void *near_address) {
    (void)near_address;
    return arena + 0x4000;
}
static int player_tail_page_rx(void *page) {
    ++page_rx_calls;
    if (page_rx_calls == fail_page_rx)
        return 0;
    is_stub_rx = 1;
    return !mprotect(page, GATE_PAGE, PROT_READ | PROT_EXEC);
}
static int player_tail_is_rx(void *address) {
    return address == arena + 0x4000 ? is_stub_rx : is_code_rx;
}
static int player_tail_open(void *address) {
    ++open_calls;
    if (open_calls == fail_open)
        return 0;
    is_code_rx = 0;
    return !mprotect((void *)((uintptr_t)address & ~(uintptr_t)(GATE_PAGE - 1)), GATE_PAGE,
                     PROT_READ | PROT_WRITE | PROT_EXEC);
}
static int player_tail_close(void *address) {
    ++close_calls;
    if (close_calls == fail_close)
        return 0;
    is_code_rx = 1;
    return !mprotect((void *)((uintptr_t)address & ~(uintptr_t)(GATE_PAGE - 1)), GATE_PAGE,
                     PROT_READ | PROT_EXEC);
}
static int player_tail_cas(uint64_t *address, uint64_t expected, uint64_t desired) {
    ++cas_calls;
    if (cas_calls == fail_cas)
        return 0;
    return __atomic_compare_exchange_n(address, &expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static void player_tail_lock(void) {
}
static void player_tail_unlock(void) {
}
static void callback(void *semaphore, unsigned count);
#include "gate_player_control_worker_tail.h"

static void callback(void *semaphore, unsigned count) {
    assert(expected_order == 0);
    expected_order = 1;
    ++callback_calls;
    seen_semaphore = semaphore;
    seen_count = count;
    errno = EDOM;
}
static void setup(void) {
    assert(!mprotect(arena, 0x30000, PROT_READ | PROT_WRITE | PROT_EXEC));
    memset(arena, 0x90, 0x30000);
    memset(&player_tail_resident, 0, sizeof player_tail_resident);
    atomic_store(&player_tail_binding, NULL);
    open_calls = close_calls = cas_calls = page_rx_calls = 0;
    fail_open = fail_close = fail_cas = fail_page_rx = reject_pins = 0;
    callback_calls = release_calls = expected_order = 0;
    is_code_rx = 1;
    is_stub_rx = 0;
    uint64_t original = PLAYER_TAIL_WORD_ORIGINAL;
    memcpy(LX(PLAYER_TAIL_WORD_VA), &original, sizeof original);
    /* sub rsp,8; short-jump over untouched bytes to the real patched call;
     * its retained trailing 0x48 begins add rsp,8; ret. */
    const unsigned char caller[] = {0x48, 0x83, 0xec, 0x08, 0xeb, 0x04};
    const unsigned char finish[] = {0x48, 0x83, 0xc4, 0x08, 0xc3};
    memcpy(LX(0x1212760), caller, sizeof caller);
    memcpy(LX(0x121276f), finish, sizeof finish);
    assert(!mprotect(arena, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!mprotect(arena + 0x4000, GATE_PAGE, PROT_READ | PROT_WRITE));
}
static void check_elf(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(!fseek(file, (long)(PLAYER_TAIL_WORD_VA - 0x400000u), SEEK_SET));
    uint64_t word;
    assert(fread(&word, 1, sizeof word, file) == sizeof word);
    fclose(file);
    assert(word == PLAYER_TAIL_WORD_ORIGINAL);
    unsigned char bytes[8];
    memcpy(bytes, &word, sizeof bytes);
    assert(bytes[2] == 0xe8);
    int32_t displacement;
    memcpy(&displacement, bytes + 3, sizeof displacement);
    assert((uintptr_t)((int64_t)PLAYER_TAIL_CALL_VA + 5 + displacement) == PLAYER_TAIL_RELEASE_VA);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    check_elf(argv[1]);
    arena = mmap(NULL, 0x30000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(arena != MAP_FAILED);
    setup();
    reject_pins = 1;
    assert(!player_tail_install(&player_tail_resident, callback));
    assert(!player_tail_resident.page);
    setup();
    assert(!mprotect(arena, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    *(unsigned char *)LX(PLAYER_TAIL_WORD_VA) ^= 1;
    assert(!mprotect(arena, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!player_tail_install(&player_tail_resident, callback));
    assert(!player_tail_resident.page);
    int32_t impossible;
    assert(!player_tail_relative(0, UINTPTR_MAX, &impossible));
    setup();
    unsigned char original_view[0x81];
    memcpy(original_view, LX(0x1212756), sizeof original_view);
    assert(player_tail_original_view(0x1212756, original_view, sizeof original_view, callback));
    assert(!player_tail_original_view(PLAYER_TAIL_WORD_VA + 1, original_view, 7, callback));
    assert(player_tail_install(&player_tail_resident, callback));
    uint64_t patched = __atomic_load_n((uint64_t *)LX(PLAYER_TAIL_WORD_VA), __ATOMIC_SEQ_CST);
    assert((patched & UINT64_C(0xff0000000000ffff)) ==
           (PLAYER_TAIL_WORD_ORIGINAL & UINT64_C(0xff0000000000ffff)));
    int32_t displacement;
    memcpy(&displacement, (unsigned char *)&patched + 3, sizeof displacement);
    assert((uintptr_t)LX(PLAYER_TAIL_CALL_VA) + 5 + displacement == (uintptr_t)player_tail_resident.page);
    unsigned char view[0x81];
    memcpy(view, LX(0x1212756), sizeof view);
    assert(player_tail_original_view(0x1212756, view, sizeof view, callback));
    uint64_t view_word; memcpy(&view_word, view + (PLAYER_TAIL_WORD_VA - 0x1212756), 8);
    assert(view_word == PLAYER_TAIL_WORD_ORIGINAL);
    assert(!memcmp(view, original_view, sizeof view));
    /* Neither tampering with the copied word nor foreign RX stub bytes are
     * normalized, and mutation never alters the actual code word here. */
    memcpy(view, LX(0x1212756), sizeof view);
    assert(!player_tail_original_view(0x1212756, view, sizeof view, NULL));
    assert(!mprotect(arena, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    *(unsigned char *)LX(PLAYER_TAIL_WORD_VA) ^= 1;
    assert(!mprotect(arena, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!player_tail_original_view(0x1212756, view, sizeof view, callback));
    assert(!mprotect(arena, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    *(unsigned char *)LX(PLAYER_TAIL_WORD_VA) ^= 1;
    assert(!mprotect(arena, GATE_PAGE, PROT_READ | PROT_EXEC));
    view[PLAYER_TAIL_WORD_VA - 0x1212756] ^= 1;
    assert(!player_tail_original_view(0x1212756, view, sizeof view, callback));
    memcpy(view, LX(0x1212756), sizeof view);
    assert(!mprotect(player_tail_resident.page, GATE_PAGE, PROT_READ | PROT_WRITE));
    player_tail_resident.page[2] = 1;
    assert(!mprotect(player_tail_resident.page, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!player_tail_original_view(0x1212756, view, sizeof view, callback));
    assert(!mprotect(player_tail_resident.page, GATE_PAGE, PROT_READ | PROT_WRITE));
    player_tail_resident.page[2] = 0;
    assert(!mprotect(player_tail_resident.page, GATE_PAGE, PROT_READ | PROT_EXEC));
    is_stub_rx = 0;
    assert(!player_tail_original_view(0x1212756, view, sizeof view, callback));
    is_stub_rx = 1;
    assert(player_tail_original_view(0x1212756, view, sizeof view, callback));
    errno = ERANGE;
    ((void (*)(void *, unsigned))LX(0x1212760))((void *)0x1234, 7);
    assert(callback_calls == 1 && release_calls == 1 && expected_order == 2 &&
           seen_semaphore == (void *)0x1234 && seen_count == 7 && seen_errno == ERANGE);
    assert(player_tail_restore(&player_tail_resident));
    assert(__atomic_load_n((uint64_t *)LX(PLAYER_TAIL_WORD_VA), __ATOMIC_SEQ_CST) ==
           PLAYER_TAIL_WORD_ORIGINAL);
    assert(player_tail_resident.page && atomic_load(&player_tail_binding) == &player_tail_resident);
    setup();
    fail_open = 1;
    assert(!player_tail_install(&player_tail_resident, callback));
    assert(!player_tail_resident.word_owned && !player_tail_resident.code_permission_pending);
    setup();
    fail_cas = 1;
    assert(!player_tail_install(&player_tail_resident, callback));
    assert(!player_tail_resident.word_owned && !player_tail_resident.code_permission_pending);
    setup();
    fail_close = 1;
    assert(!player_tail_install(&player_tail_resident, callback));
    assert(player_tail_resident.word_owned && player_tail_resident.code_permission_pending &&
           player_tail_resident.uncertain);
    assert(!player_tail_restore(&player_tail_resident));
    assert(!player_tail_resident.word_owned && !player_tail_resident.code_permission_pending &&
           player_tail_resident.uncertain);
    setup();
    fail_page_rx = 1;
    assert(!player_tail_install(&player_tail_resident, callback));
    assert(player_tail_resident.page_permission_pending && player_tail_resident.uncertain);
    assert(!player_tail_restore(&player_tail_resident));
    setup();
    assert(player_tail_install(&player_tail_resident, callback));
    assert(!mprotect(arena, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    __atomic_store_n((uint64_t *)LX(PLAYER_TAIL_WORD_VA), UINT64_C(0x12345678), __ATOMIC_SEQ_CST);
    assert(!mprotect(arena, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!player_tail_restore(&player_tail_resident));
    assert(player_tail_resident.word_owned && player_tail_resident.uncertain &&
           __atomic_load_n((uint64_t *)LX(PLAYER_TAIL_WORD_VA), __ATOMIC_SEQ_CST) == UINT64_C(0x12345678));
    assert(!munmap(arena, 0x30000));
    puts("worker tail hook real-stub, ELF word, restore and faults PASS");
}
