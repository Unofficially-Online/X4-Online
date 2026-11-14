#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <execinfo.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

extern int cursor_test_entry(void *controller, void *output);
extern void cursor_test_resume(void);
extern void player_cursor_native_bridge(void);
__attribute__((visibility("hidden"))) uintptr_t player_cursor_native_resume = (uintptr_t)cursor_test_resume;
static unsigned calls;
__attribute__((visibility("hidden"))) void player_cursor_native_dispatch(uintptr_t controller) {
    int saved = errno;
    void *frames[16];
    assert(controller == 0x12340000 && backtrace(frames, 16) >= 3);
    ++calls;
    __asm__ volatile("xor %%rax, %%rax; xor %%rdx, %%rdx; xor %%r11, %%r11; pxor %%xmm0, %%xmm0"
                     ::: "rax", "rdx", "r11", "xmm0", "cc");
    errno = saved;
}
__asm__(
".text\n"
".global cursor_test_entry\n.type cursor_test_entry,@function\n"
"cursor_test_entry:\n.cfi_startproc\n"
"push %r15\n.cfi_adjust_cfa_offset 8\n.cfi_offset %r15,-16\n"
"push %r14\n.cfi_adjust_cfa_offset 8\n.cfi_offset %r14,-24\n"
"push %r13\n.cfi_adjust_cfa_offset 8\n.cfi_offset %r13,-32\n"
"push %r12\n.cfi_adjust_cfa_offset 8\n.cfi_offset %r12,-40\n"
"push %rbp\n.cfi_adjust_cfa_offset 8\n.cfi_offset %rbp,-48\n"
"push %rbx\n.cfi_adjust_cfa_offset 8\n.cfi_offset %rbx,-56\n"
"mov %rdi,%rbx\nsub $0x288,%rsp\n.cfi_adjust_cfa_offset 648\n"
"movabs $0x1122334455667788,%rax\nmovabs $0x8877665544332211,%rdx\n"
"movabs $0x1020304050607080,%r11\npcmpeqd %xmm0,%xmm0\njmp player_cursor_native_bridge\n"
".global cursor_test_resume\ncursor_test_resume:\n"
"movdqu %xmm0,(%rsi)\nmov %rax,16(%rsi)\nmov %rdx,24(%rsi)\nmov %r11,32(%rsi)\nsete %al\nmovzbl %al,%eax\n"
"add $0x288,%rsp\n.cfi_adjust_cfa_offset -648\n"
"pop %rbx\n.cfi_adjust_cfa_offset -8\n"
"pop %rbp\n.cfi_adjust_cfa_offset -8\n"
"pop %r12\n.cfi_adjust_cfa_offset -8\n"
"pop %r13\n.cfi_adjust_cfa_offset -8\n"
"pop %r14\n.cfi_adjust_cfa_offset -8\n"
"pop %r15\n.cfi_adjust_cfa_offset -8\nret\n.cfi_endproc\n"
".size cursor_test_entry,.-cursor_test_entry\n");
int main(void) {
    unsigned char *page = mmap((void *)0x3d8d000, 4096, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(page != MAP_FAILED);
    unsigned char output[40];
    memset(output, 0, sizeof output);
    errno = EDOM;
    assert(cursor_test_entry((void *)0x12340000, output) == 1);
    assert(errno == EDOM && calls == 1);
    for (unsigned index = 0; index < 16; ++index) assert(output[index] == 0xff);
    uint64_t values[3];
    memcpy(values, output + 16, sizeof values);
    assert(values[0] == UINT64_C(0x1122334455667788) &&
           values[1] == UINT64_C(0x8877665544332211) &&
           values[2] == UINT64_C(0x1020304050607080));
    page[0xa7a] = 1;
    assert(cursor_test_entry((void *)0x12340000, output) == 0 && calls == 2);
    assert(!munmap(page, 4096));
    return 0;
}
