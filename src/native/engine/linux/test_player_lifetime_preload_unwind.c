#define _GNU_SOURCE
#include <assert.h>
#include <execinfo.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "player_lifetime_preload_emit.h"

_Atomic uint32_t plp_entry_count;
void *plp_original_target0;
void *plp_original_target1;
#include "player_lifetime_preload_wrappers.h"
static _Atomic(const plp_binding *) binding;
static unsigned char *native_page;
static unsigned char *stub;

typedef struct { uint64_t first, second; } integer_pair;
typedef struct { double first, second; } vector_pair;
typedef struct { uint64_t first; double second; } mixed_pair;

__attribute__((noinline)) static integer_pair integer_result(void *object, void *other, int flag) {
    assert(object == (void *)0x11 && other == (void *)0x22 && flag == 7);
    void *frames[32];
    int count = backtrace(frames, 32);
    int found_wrapper = 0;
    for (int index = 0; index < count; ++index) {
        uintptr_t address = (uintptr_t)frames[index];
        if (address > (uintptr_t)plp_call_original0 && address < (uintptr_t)plp_call_original0 + 22) {
            found_wrapper = 1;
        }
    }
    assert(found_wrapper && count >= 4);
    return (integer_pair){UINT64_C(0x123456789abcdef0), UINT64_C(0xfedcba9876543210)};
}

__attribute__((noinline)) static vector_pair vector_result(void) {
    return (vector_pair){1.25, -7.5};
}

__attribute__((noinline)) static mixed_pair mixed_result(void) {
    return (mixed_pair){UINT64_C(0x123456789abcdef0), -7.5};
}

static void select_target(uintptr_t target) {
    assert(!mprotect(native_page, PLP_PAGE, PROT_READ | PROT_WRITE));
    memcpy(native_page + 19, &target, sizeof target);
    assert(!mprotect(native_page, PLP_PAGE, PROT_READ | PROT_EXEC));
}

int main(void) {
    native_page = mmap(NULL, PLP_PAGE * 2, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(native_page != MAP_FAILED);
    memcpy(native_page, &plp_original[0], 8);
    const unsigned char tail[] = {0x5d, 0x41, 0x5c, 0x41, 0x5d, 0xff, 0x25, 0, 0, 0, 0};
    memcpy(native_page + 8, tail, sizeof tail);
    stub = native_page + PLP_PAGE;
    memset(stub, 0xcc, PLP_PAGE);
    plp_emit(stub, (uintptr_t)&binding, (uintptr_t)&plp_entry_count, 0,
             (uintptr_t)native_page, 0, (uintptr_t)plp_call_original0);
    plp_original_target0 = stub + PLP_TRAMPOLINE_OFFSET;
    assert(!mprotect(stub, PLP_PAGE, PROT_READ | PROT_EXEC));
    select_target((uintptr_t)integer_result);
    integer_pair a = ((integer_pair (*)(void *, void *, int))stub)((void *)0x11, (void *)0x22, 7);
    assert(a.first == UINT64_C(0x123456789abcdef0) && a.second == UINT64_C(0xfedcba9876543210));
    select_target((uintptr_t)vector_result);
    vector_pair b = ((vector_pair (*)(void))stub)();
    assert(b.first == 1.25 && b.second == -7.5);
    select_target((uintptr_t)mixed_result);
    mixed_pair c = ((mixed_pair (*)(void))stub)();
    assert(c.first == UINT64_C(0x123456789abcdef0) && c.second == -7.5);
    assert(!atomic_load(&plp_entry_count));
    puts("lifetime compiled wrapper preserves integer/vector returns and stack unwind PASS");
}
