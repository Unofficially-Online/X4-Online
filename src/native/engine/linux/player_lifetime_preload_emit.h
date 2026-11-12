#ifndef X4_PLAYER_LIFETIME_PRELOAD_EMIT_H
#define X4_PLAYER_LIFETIME_PRELOAD_EMIT_H
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#define PLP_REPLACE UINT64_C(0x1316f40)
#define PLP_DESTRUCT UINT64_C(0x164c1f0)
#define PLP_VTABLE UINT64_C(0x3b7c490)
#define PLP_REPLACE_WORD UINT64_C(0x55d4894154415541)
#define PLP_DESTRUCT_WORD UINT64_C(0x530297058d485541)
#define PLP_PAGE 4096u
#define PLP_VERSION 5u
#define PLP_TRAMPOLINE_OFFSET 256u
_Static_assert(sizeof(_Atomic uint32_t) == 4, "replacement counter must be 32 bits");
/* Each published binding is immutable. Its replacement counter is a
 * separate atomic word, claimed once by the construction stub. */
typedef struct {
    void *ship;
    void *controller;
    void (*ship_fatal)(void *);
    void (*controller_fatal)(void *);
    uint32_t owner_thread;
    uint32_t construction;
    _Atomic uint32_t *replacement_counter;
} plp_binding;
typedef struct {
    unsigned version;
    unsigned installed;
    const unsigned char *pages[2];
    const void *binding_slot;
    const void *entry_count;
    const void *wrappers[2];
    const void *original_slots[2];
} plp_information;
static const uint64_t plp_sites[2] = {PLP_REPLACE, PLP_DESTRUCT};
static const uint64_t plp_original[2] = {PLP_REPLACE_WORD, PLP_DESTRUCT_WORD};
static inline void plp_u64(unsigned char *destination, uintptr_t value) { memcpy(destination, &value, 8); }
static inline void plp_jump(unsigned char *destination, uintptr_t target) {
    const unsigned char jump[] = {0xff,0x25,0,0,0,0};
    memcpy(destination, jump, sizeof jump);
    plp_u64(destination + 6, target);
}
/* Reconstruct the immutable compiled wrapper including its RIP-relative
 * storage references. The caller separately authenticates both storage slots. */
static inline int plp_wrapper_bytes(unsigned char output[22], uintptr_t wrapper,
                                    uintptr_t original_slot, uintptr_t entry_count) {
    static const unsigned char code[22] = {
        0x48,0x83,0xec,0x08, 0xff,0x15,0,0,0,0, 0x48,0x83,0xc4,0x08,
        0xf0,0xff,0x0d,0,0,0,0, 0xc3
    };
    intptr_t original_delta = (intptr_t)original_slot - (intptr_t)(wrapper + 10);
    intptr_t count_delta = (intptr_t)entry_count - (intptr_t)(wrapper + 21);
    if (original_delta < INT32_MIN || original_delta > INT32_MAX ||
        count_delta < INT32_MIN || count_delta > INT32_MAX) {
        return 0;
    }
    memcpy(output, code, sizeof code);
    int32_t original_relative = (int32_t)original_delta;
    int32_t count_relative = (int32_t)count_delta;
    memcpy(output + 6, &original_relative, 4);
    memcpy(output + 17, &count_relative, 4);
    return 1;
}

/* The generated code uses only volatile registers. Each short branch is
 * filled after both destinations are known, so label positions stay explicit. */
static inline void plp_branch(unsigned char *page, size_t displacement, size_t destination) {
    page[displacement] = (unsigned char)(destination - (displacement + 1));
}
static inline size_t plp_emit(unsigned char *page, uintptr_t binding_slot, uintptr_t entry_count,
                              unsigned index, uintptr_t site,
                              uintptr_t vtable, uintptr_t wrapper) {
    const unsigned char load_binding[] = {0x48, 0xb8, 0,0,0,0,0,0,0,0, 0x48,0x8b,0x00,
                                          0x48,0x85,0xc0, 0x74,0};
    /* The count covers the entire relocated native call. The first instruction
     * sequence increments before reading the binding; a published binding plus
     * a subsequent zero count therefore drains all pre-publication calls. */
    const unsigned char enter[] = {0x49,0xbb,0,0,0,0,0,0,0,0, 0xf0,0x41,0xff,0x03};
    memcpy(page, enter, sizeof enter);
    plp_u64(page + 2, entry_count);
    size_t position = sizeof enter;
    memcpy(page + position, load_binding, sizeof load_binding);
    plp_u64(page + position + 2, binding_slot);
    size_t unarmed_branch = position + 17;
    position += sizeof load_binding;
    size_t unrelated_branch;
    size_t protected_branch = 0;
    size_t owner_branch = 0;
    size_t repeated_branch = 0;
    size_t admitted_branch = 0;
    size_t restore_binding = 0;
    if (!index) {
        /* Only the preparing TID may enter replacement, and it may do so
         * once. LOCK BTS claims the resident counter before native code runs,
         * so nested or repeated attempts fail before mutation. The verified
         * replacement ABI uses RDI, RSI and RDX; syscall clobbers RCX/R11. */
        const unsigned char replacement[] = {
            0x48,0x3b,0x78,0, 0x75,0,
            0x83,0x78,0,0, 0x74,0,
            0x49,0x89,0xc2, 0xb8,0xba,0,0,0, 0x0f,0x05,
            0x41,0x3b,0x42,0, 0x75,0,
            0x4d,0x8b,0x5a,0,
            0xf0,0x41,0x0f,0xba,0x2b,0x00,
            0x72,0, 0xeb,0,
            0x4c,0x89,0xd0
        };
        size_t start = position;
        memcpy(page + position, replacement, sizeof replacement);
        page[start + 3] = offsetof(plp_binding, ship);
        unrelated_branch = start + 5;
        page[start + 8] = offsetof(plp_binding, construction);
        protected_branch = start + 11;
        page[start + 25] = offsetof(plp_binding, owner_thread);
        owner_branch = start + 27;
        page[start + 31] = offsetof(plp_binding, replacement_counter);
        repeated_branch = start + 39;
        admitted_branch = start + 41;
        restore_binding = start + 42;
        position += sizeof replacement;
    } else {
        /* Before Take completes, the new object can be published without a
         * ship backpointer. Reject every PlayerController destructor during
         * this short phase. Afterwards guard only the exact controller. */
        const unsigned char destruction[] = {
            0x83,0x78,0,0, 0x75,0,
            0x48,0x3b,0x78,0, 0x75,0
        };
        size_t start = position;
        memcpy(page + position, destruction, sizeof destruction);
        page[start + 2] = offsetof(plp_binding, construction);
        protected_branch = start + 5;
        page[start + 9] = offsetof(plp_binding, controller);
        unrelated_branch = start + 11;
        position += sizeof destruction;
    }
    size_t fatal = position;
    const unsigned char call_fatal[] = {0x48,0x83,0xec,0x08, 0xff,0x50,0, 0x0f,0x0b};
    memcpy(page + position, call_fatal, sizeof call_fatal);
    page[position + 6] = (unsigned char)(index ? offsetof(plp_binding, controller_fatal) :
                                                 offsetof(plp_binding, ship_fatal));
    position += sizeof call_fatal;
    size_t original = position;
    plp_branch(page, unarmed_branch, original);
    plp_branch(page, unrelated_branch, original);
    if (index)
        plp_branch(page, protected_branch, fatal);
    else {
        plp_branch(page, protected_branch, fatal);
        plp_branch(page, owner_branch, restore_binding);
        plp_branch(page, repeated_branch, restore_binding);
        plp_branch(page, admitted_branch, original);
    }
    /* The owned entry stub tail-jumps to a compiled wrapper with CFI. The
     * relocated prologue is separately placed at a fixed page offset. */
    plp_jump(page + position, wrapper);
    position = PLP_TRAMPOLINE_OFFSET;
    if (!index) {
        memcpy(page + position, &plp_original[0], 8);
        position += 8;
        plp_jump(page + position, site + 8);
    } else {
        const unsigned char prologue[] = {0x41,0x55,0x48,0xb8,0,0,0,0,0,0,0,0};
        memcpy(page + position, prologue, sizeof prologue);
        plp_u64(page + position + 4, vtable);
        position += sizeof prologue;
        plp_jump(page + position, site + 9);
    }
    return position + 14;
}
#endif
